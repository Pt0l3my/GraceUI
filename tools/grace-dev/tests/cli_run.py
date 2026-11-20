#!/usr/bin/env python3
"""grace-dev run end to end (D168), for CTest on Linux: started on a free loopback port with both built-in spaces, its
page and a ticket fetched as a browser fetches them, then stopped by SIGTERM (and, a second time, by Ctrl+C's SIGINT).
Each run must say it is ready, exit with 0 within its time, and print neither the session cookie nor the ticket
(invariant 5). Run it with python3 -I, naming the grace-dev program."""
import http.client
import json
import signal
import socket
import subprocess
import sys
import threading
import time


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def collect(stream, into):
    for line in stream:
        into.append(line)


def run_once(program, stop):
    port = free_port()
    process = subprocess.Popen(
        [program, "--space", "demo", "--space", "graceui", "--port", str(port), "--speed", "50", "--loop"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    out, err = [], []
    readers = [threading.Thread(target=collect, args=(process.stdout, out), daemon=True),
               threading.Thread(target=collect, args=(process.stderr, err), daemon=True)]
    for reader in readers:
        reader.start()
    try:
        ready = f"Ready: http://127.0.0.1:{port}/"
        deadline = time.monotonic() + 15
        while not any(line.startswith(ready) for line in out):
            if process.poll() is not None or time.monotonic() > deadline:
                raise AssertionError(f"grace-dev was not ready:\n{''.join(out)}{''.join(err)}")
            time.sleep(0.05)

        host = f"127.0.0.1:{port}"
        page = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        page.request("GET", "/", headers={"Host": host})
        reply = page.getresponse()
        body = reply.read().decode("utf-8")
        cookie = (reply.getheader("Set-Cookie") or "").split(";", 1)[0]
        page.close()
        assert reply.status == 200, reply.status
        assert "grace-dev is running" in body, body
        assert cookie.startswith("en_session=") and len(cookie) == len("en_session=") + 43, "no session cookie"

        post = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        post.request("POST", "/api/ticket", body="{}", headers={
            "Host": host, "Origin": f"http://{host}", "Content-Type": "application/json", "Cookie": cookie,
            "Sec-Fetch-Site": "same-origin", "Sec-Fetch-Mode": "cors"})
        reply = post.getresponse()
        answer = json.loads(reply.read().decode("utf-8"))
        post.close()
        assert reply.status == 200, reply.status
        ticket = answer["ticket"]
        assert len(ticket) == 43, "no ticket"

        process.send_signal(stop)
        status = process.wait(timeout=10)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    for reader in readers:
        reader.join(timeout=5)
    printed = "".join(out) + "".join(err)
    assert status == 0, f"grace-dev exited with {status}:\n{printed}"
    assert "grace-dev: stopping." in printed, printed
    assert cookie.split("=", 1)[1] not in printed, "the session cookie was printed"
    assert ticket not in printed, "the ticket was printed"
    return printed


def main(argv):
    if len(argv) != 2:
        print("usage: cli_run.py <grace-dev>", file=sys.stderr)
        return 2
    try:
        first = run_once(argv[1], signal.SIGTERM)
        run_once(argv[1], signal.SIGINT)
    except (AssertionError, OSError, subprocess.SubprocessError, KeyError, ValueError) as error:
        print(f"cli_run: {error}", file=sys.stderr)
        return 1
    print(first, end="")
    print("cli_run: both runs passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
