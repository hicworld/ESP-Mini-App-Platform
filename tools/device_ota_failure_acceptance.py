"""Verify rejected OTA downloads leave the confirmed image and apps intact."""
import argparse
import re
import time
from pathlib import Path

import serial


STATUS = re.compile(r"STATUS version=(\S+) partition=(\S+) ota_state=(\d+) ")
APP = re.compile(r"^(com\.[a-z0-9.]+) \d+\.\d+\.\d+ ")


class Board:
    def __init__(self, port: str, log):
        self.port = port
        self.log = log
        self.connection = None

    def note(self, text: str):
        print(text, flush=True)
        self.log.write(text + "\n")
        self.log.flush()

    def connect(self, timeout: float = 30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                self.connection = serial.Serial(
                    self.port, 115200, timeout=0.25, write_timeout=5
                )
                self.connection.dtr = True
                self.connection.rts = False
                time.sleep(1)
                return
            except serial.SerialException:
                time.sleep(0.5)
        raise TimeoutError(f"could not open {self.port}")

    def close(self):
        if self.connection:
            self.connection.close()
            self.connection = None

    def command(self, command: str, timeout: float = 20):
        self.note("> " + command)
        self.connection.write((command + "\n").encode())
        lines = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.connection.readline().decode(errors="replace").strip()
            if line:
                lines.append(line)
                self.note(line)
                if line.startswith("result: "):
                    if line != "result: ESP_OK":
                        raise AssertionError(f"{command}: {line}")
                    return "\n".join(lines)
        raise TimeoutError(command)

    def rejected_ota(self, url: str, timeout: float = 75):
        self.note("> ota " + url)
        self.connection.write(("ota " + url + "\n").encode())
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.connection.readline().decode(errors="replace").strip()
            except serial.SerialException as error:
                raise AssertionError("rejected OTA rebooted or disconnected the board") from error
            if not line:
                continue
            self.note(line)
            if line.startswith("result: "):
                if line == "result: ESP_OK":
                    raise AssertionError("invalid OTA was accepted")
                return line
        raise TimeoutError("invalid OTA did not return an error")

    def status(self):
        output = self.command("status")
        match = STATUS.search(output)
        if not match:
            raise AssertionError("firmware does not expose OTA status fields")
        return match.groups(), output

    def apps(self):
        return {
            match.group(1)
            for line in self.command("list").splitlines()
            if (match := APP.match(line))
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--invalid-image-url", required=True)
    parser.add_argument("--missing-image-url", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for url in (args.invalid_image_url, args.missing_image_url):
        if not url.startswith("https://"):
            raise SystemExit("OTA test URLs must use HTTPS")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as log:
        board = Board(args.port, log)
        try:
            board.connect()
            baseline, _ = board.status()
            apps = board.apps()
            if not apps:
                raise AssertionError("no installed apps available for persistence check")
            for label, url in (
                ("non-firmware HTTPS payload", args.invalid_image_url),
                ("HTTP 404", args.missing_image_url),
            ):
                board.note("BEGIN " + label)
                board.rejected_ota(url)
                current, status = board.status()
                if current != baseline:
                    raise AssertionError("rejected OTA changed the running image: " + status)
                if board.apps() != apps:
                    raise AssertionError("rejected OTA changed the application registry")
            board.command("store_refresh", timeout=45)
            board.note(
                "OTA FAILURE ACCEPTANCE PASS: invalid payload and 404 were rejected "
                "without reboot, slot change or app loss"
            )
        finally:
            board.close()


if __name__ == "__main__":
    main()
