#!/usr/bin/env python3
"""Integration tests for the chat server: connects raw TCP clients and checks the protocol."""
import socket
import subprocess
import sys
import time

PORT = 15000
passed = failed = 0


def check(name, cond):
    global passed, failed
    if cond:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name}")


class Client:
    def __init__(self):
        self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=2)
        self.buf = b""

    def send(self, line):
        self.sock.sendall(line.encode() + b"\n")

    def expect(self, text, timeout=2.0):
        """Reads until `text` appears; returns True if it did."""
        deadline = time.time() + timeout
        while text.encode() not in self.buf:
            remaining = deadline - time.time()
            if remaining <= 0:
                return False
            self.sock.settimeout(remaining)
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                return False
            if not data:
                return False
            self.buf += data
        idx = self.buf.index(text.encode()) + len(text)
        self.buf = self.buf[idx:]
        return True

    def close(self):
        self.sock.close()


def main():
    server = subprocess.Popen(["./bin/chat_server", "-p", str(PORT)], stdout=subprocess.DEVNULL)
    time.sleep(0.5)
    try:
        print("Running chat server tests")
        alice, bob = Client(), Client()
        check("welcome message", alice.expect("Choose a nickname") and bob.expect("Choose a nickname"))

        alice.send("alice")
        check("nickname accepted", alice.expect("Hello alice"))
        bob.send("alice")
        check("duplicate nickname refused", bob.expect("Invalid or already used"))
        bob.send("bad nick!")
        check("invalid nickname refused", bob.expect("Invalid or already used"))
        bob.send("bob")
        check("second nickname accepted", bob.expect("Hello bob"))
        check("join is announced", alice.expect("* bob joined the chat"))

        alice.send("hello bob")
        check("message is broadcast", bob.expect("[alice] hello bob"))

        bob.sock.sendall(b"split ")
        time.sleep(0.2)
        bob.sock.sendall(b"message\r\n")
        check("partial reads and CRLF handled", alice.expect("[bob] split message\n"))

        alice.send("/who")
        check("/who lists users", alice.expect("Online: alice bob"))
        alice.send("/nope")
        check("unknown command reported", alice.expect("Unknown command"))

        alice.send("/quit")
        check("/quit says bye", alice.expect("Bye!"))
        check("leave is announced", bob.expect("* alice left the chat"))
        alice.close()
        bob.close()
    finally:
        server.terminate()
        server.wait()

    print(f"\n{passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
